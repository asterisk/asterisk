"""add pjsip debug option

Revision ID: 21e526ad3040
Revises: 2fc7930b41b3
Create Date: 2014-01-30 10:44:02.297455

"""

# revision identifiers, used by Alembic.
revision = '21e526ad3040'
down_revision = '2fc7930b41b3'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_globals', sa.Column('debug', sa.String(40)))

def downgrade():
    with op.batch_alter_table('ps_globals') as batch_op:
        batch_op.drop_column('debug')
