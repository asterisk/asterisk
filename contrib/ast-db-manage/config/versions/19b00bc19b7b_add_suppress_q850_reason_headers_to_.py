"""add suppress_q850_reason_headers to endpoint

Revision ID: 19b00bc19b7b
Revises: 0be05c3a8225
Create Date: 2018-07-06 06:30:32.196669

"""

# revision identifiers, used by Alembic.
revision = '19b00bc19b7b'
down_revision = '0be05c3a8225'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']

def upgrade():
    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)
    op.add_column('ps_endpoints', sa.Column('suppress_q850_reason_header', yesno_values))

def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_suppress_q850_reason_header_yesno_values','ps_endpoints')
    op.drop_column('ps_endpoints', 'suppress_q850_reason_header')
