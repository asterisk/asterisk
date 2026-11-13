"""add follow_offer to rtp_ipv6

Revision ID: a4d901ba030a
Revises: 2b45fd748a4f
Create Date: 2026-11-13 10:00:00.000000

"""

# revision identifiers, used by Alembic.
revision = 'a4d901ba030a'
down_revision = '2b45fd748a4f'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

AST_BOOL_NAME = 'ast_bool_values'
AST_BOOL_VALUES = [ '0', '1',
                    'off', 'on',
                    'false', 'true',
                    'no', 'yes' ]

RTP_IPV6_NAME = 'pjsip_rtp_ipv6_values'
RTP_IPV6_VALUES = AST_BOOL_VALUES + [ 'follow_offer' ]

ast_bool_values = ENUM(*AST_BOOL_VALUES, name=AST_BOOL_NAME, create_type=False)
rtp_ipv6_values = ENUM(*RTP_IPV6_VALUES, name=RTP_IPV6_NAME, create_type=False)

def upgrade():
    if op.get_context().bind.dialect.name == 'postgresql':
        rtp_ipv6_values.create(op.get_bind(), checkfirst=False)

    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.alter_column('rtp_ipv6',
                              type_=rtp_ipv6_values,
                              existing_type=ast_bool_values,
                              postgresql_using='rtp_ipv6::text::{}'.format(RTP_IPV6_NAME))

def downgrade():
    # follow_offer behaves like yes for everything but answers
    op.execute("UPDATE ps_endpoints SET rtp_ipv6 = 'yes' WHERE rtp_ipv6 = 'follow_offer'")

    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.alter_column('rtp_ipv6',
                              type_=ast_bool_values,
                              existing_type=rtp_ipv6_values,
                              postgresql_using='rtp_ipv6::text::{}'.format(AST_BOOL_NAME))

    if op.get_context().bind.dialect.name == 'postgresql':
        rtp_ipv6_values.drop(op.get_bind(), checkfirst=False)
