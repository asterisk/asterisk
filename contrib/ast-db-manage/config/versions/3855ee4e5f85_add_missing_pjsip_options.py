"""add_missing_pjsip_options

Revision ID: 3855ee4e5f85
Revises: 4c573e7135bd
Create Date: 2014-03-28 10:49:04.637730

"""

# revision identifiers, used by Alembic.
revision = '3855ee4e5f85'
down_revision = '4c573e7135bd'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_endpoints', sa.Column('message_context', sa.String(40)))
    op.add_column('ps_contacts', sa.Column('user_agent', sa.String(40)))


def downgrade():
    with op.batch_alter_table('ps_contacts') as batch_op:
        batch_op.drop_column('user_agent')
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.drop_column('message_context')
